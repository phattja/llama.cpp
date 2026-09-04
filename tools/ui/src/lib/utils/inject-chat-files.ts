import { AttachmentType, MessageRole } from '$lib/enums';
import { ChatUploadsService } from '$lib/services/chat-uploads.service';
import type { DatabaseMessage, DatabaseMessageExtra } from '$lib/types/database';

/**
 * File object injected into tool arguments so MCP tools can read chat
 * attachments the model did not pass itself. Prefer a server filesystem
 * path; fall back to a data URI if the upload fails.
 */
export interface ChatFileInjection {
	id: string;
	type: 'image' | 'file';
	name: string;
	mimeType: string;
	url: string;
	data: string;
	base64: string;
	path?: string;
}

function mimeFromDataUrl(url: string): string | undefined {
	const match = /^data:([^;,]+)/i.exec(url);

	return match?.[1];
}

function stripDataUrl(payload: string): string {
	const comma = payload.indexOf(',');

	return comma >= 0 ? payload.slice(comma + 1) : payload;
}

function injectionFromPayload(
	name: string,
	mimeType: string,
	type: 'image' | 'file',
	payload: string,
	path?: string
): ChatFileInjection {
	const url = path ?? payload;

	return {
		base64: url,
		data: path ?? stripDataUrl(payload),
		id: path ?? name,
		mimeType,
		name,
		path,
		type,
		url
	};
}

async function uploadOrDataUri(
	name: string,
	mimeType: string,
	type: 'image' | 'file',
	payload: string
): Promise<ChatFileInjection> {
	const raw = stripDataUrl(payload);

	try {
		const uploaded = await ChatUploadsService.upload(name, mimeType, raw);

		return injectionFromPayload(name, mimeType, type, payload, uploaded.path);
	} catch (error) {
		console.warn(`[chat-files] upload failed for ${name}, using data URI:`, error);

		return injectionFromPayload(name, mimeType, type, payload);
	}
}

function extraServerPath(extra: DatabaseMessageExtra): string | undefined {
	if ('serverPath' in extra && typeof extra.serverPath === 'string' && extra.serverPath.trim()) {
		return extra.serverPath.trim();
	}

	return undefined;
}

export async function extrasToChatFiles(extras: DatabaseMessageExtra[]): Promise<ChatFileInjection[]> {
	const files: ChatFileInjection[] = [];

	for (const extra of extras) {
		const existingPath = extraServerPath(extra);

		if (extra.type === AttachmentType.IMAGE && (extra.base64Url || existingPath)) {
			if (existingPath) {
				files.push(
					injectionFromPayload(
						extra.name,
						mimeFromDataUrl(extra.base64Url) || 'image/png',
						'image',
						extra.base64Url || existingPath,
						existingPath
					)
				);
			} else if (extra.base64Url) {
				files.push(
					await uploadOrDataUri(
						extra.name,
						mimeFromDataUrl(extra.base64Url) || 'image/png',
						'image',
						extra.base64Url
					)
				);
			}
			continue;
		}

		if (extra.type === AttachmentType.PDF && (extra.base64Data || existingPath)) {
			const mime = 'application/pdf';
			if (existingPath) {
				files.push(injectionFromPayload(extra.name, mime, 'file', extra.base64Data || existingPath, existingPath));
			} else if (extra.base64Data) {
				const payload = extra.base64Data.startsWith('data:')
					? extra.base64Data
					: `data:${mime};base64,${extra.base64Data}`;

				files.push(await uploadOrDataUri(extra.name, mime, 'file', payload));
			}
			continue;
		}

		if (
			(extra.type === AttachmentType.AUDIO || extra.type === AttachmentType.VIDEO) &&
			existingPath
		) {
			files.push(
				injectionFromPayload(extra.name, extra.mimeType, 'file', extra.base64Data || existingPath, existingPath)
			);
			continue;
		}

		if (extra.type === AttachmentType.TEXT && existingPath) {
			files.push(injectionFromPayload(extra.name, 'text/plain', 'file', extra.content, existingPath));
		}
	}

	return files;
}

/** Text the model sees so it can pass attachment paths to tools instead of asking the user. */
export function formatAttachedFilesForModel(files: ChatFileInjection[]): string {
	if (files.length === 0) {
		return '';
	}

	const lines = files.map((file) => {
		const loc = file.path || file.url;
		return `- ${file.name} (${file.mimeType}): ${loc}`;
	});

	return [
		'[Attached files on this llama-server host. Pass these paths to file tools. Do not ask the user to re-upload or provide a path.]',
		...lines
	].join('\n');
}

/**
 * Last user message extras in a conversation (chat-level attachments).
 * API-normalized messages without `extra` are skipped.
 */
export function collectLastUserMessageExtras(messages: unknown[]): DatabaseMessageExtra[] {
	let last: DatabaseMessageExtra[] = [];

	for (const raw of messages) {
		if (!raw || typeof raw !== 'object') continue;

		const msg = raw as Partial<DatabaseMessage>;

		if (msg.role !== MessageRole.USER && msg.role !== 'user') continue;

		if (Array.isArray(msg.extra) && msg.extra.length > 0) {
			last = msg.extra;
		}
	}

	return last;
}

function isBlank(value: unknown): boolean {
	return value === undefined || value === null || value === '';
}

/**
 * Inject `__files__`, `__file__`, and `__image__` into tool-call arguments.
 * Uploads attachments to the server first so tools receive a filesystem path
 * instead of a data URI. Does not overwrite values the model already set.
 */
export async function injectChatFilesIntoToolArgs(
	args: Record<string, unknown>,
	extras: DatabaseMessageExtra[]
): Promise<Record<string, unknown>> {
	const files = await extrasToChatFiles(extras);

	if (files.length === 0) {
		return args;
	}

	const firstImage = files.find((file) => file.type === 'image');
	const firstFile = files.find((file) => file.type === 'file') ?? files[0];
	const out: Record<string, unknown> = { ...args };
	const toolPath = firstFile.path ?? firstFile.url;

	if (out.__files__ === undefined) {
		out.__files__ = files;
	}

	if (out.__file__ === undefined) {
		out.__file__ = firstFile;
	}

	if (out.__image__ === undefined && firstImage) {
		out.__image__ = firstImage.path ?? firstImage.url;
	}

	if (isBlank(out.image)) {
		out.image = firstImage?.path ?? firstImage?.url ?? toolPath;
	}

	if (isBlank(out.file_id)) {
		out.file_id = firstFile.path ?? firstFile.id;
	}

	if (isBlank(out.file)) {
		out.file = toolPath;
	}

	if (isBlank(out.path)) {
		out.path = toolPath;
	}

	return out;
}
